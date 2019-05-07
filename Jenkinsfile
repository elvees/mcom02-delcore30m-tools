void prepare_env(String[] packages) {
    String install_packages = '';
    if (packages)
        install_packages = String.format('pip install %s', packages.join(' '))

    stage('Prepare environment') {
        sh """
            python3 -m venv .venv
            source .venv/bin/activate
            pip install setuptools --upgrade
            pip install pip --upgrade
            ${install_packages}
        """
    }
}

node('linter') {
    stage('Checkout') {
        checkout scm
    }
    prepare_env('tox')
    stage('Tox') {
        sh """
            source .venv/bin/activate
            tox
        """
    }
}